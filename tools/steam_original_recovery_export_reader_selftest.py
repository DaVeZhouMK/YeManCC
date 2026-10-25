#!/usr/bin/env python3
"""Compare the actual old/new exportRva; build only an isolated file-reading fixture.

Example (PowerShell):
  python tools/steam_original_recovery_export_reader_selftest.py --before <saved-driver.inc> \
    --evidence <fresh-output-dir> --own-dll <existing-owned-trial.dll>
Requires Windows x64 MSVC and SDK; --vc-tools / --sdk-version may override discovery.
Never loads a DLL, includes the engine, opens another process, or controls an application.
The baseline and evidence directory are mandatory: no old evidence is overwritten.
"""
from __future__ import annotations

import argparse
import difflib
import hashlib
import json
import os
from pathlib import Path
import statistics
import struct
import subprocess
import sys
import traceback

MARKER = b'  need(b.size() > 4096, "dll-file");'


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def pin(path: Path) -> dict:
    data = path.read_bytes()
    return {"path": str(path.resolve()), "bytes": len(data), "sha256": digest(data)}


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def extract(data: bytes) -> tuple[int, int, bytes, bytes]:
    start = data.index(b'std::uint64_t exportRva(')
    end = data.index(b'\n}', start) + 2
    function = data[start:end]
    marker = function.index(MARKER)
    reader = function[function.index(b'\n') + 1:marker]
    return start, end, function, reader


def put32(buf: bytearray, offset: int, value: int) -> None:
    struct.pack_into('<I', buf, offset, value)


def pe(size: int = 24576, rva: int = 0x2800) -> bytes:
    """Safe PE64 fixture for the unchanged parser, including two named exports."""
    b = bytearray(size)
    b[:2] = b'MZ'
    put32(b, 0x3c, 0x80)
    b[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<HH', b, 0x84, 0x8664, 1)
    struct.pack_into('<H', b, 0x94, 240)
    struct.pack_into('<H', b, 0x98, 0x20b)
    put32(b, 0x98 + 108, 16)
    put32(b, 0x98 + 112, 0x1000)
    put32(b, 0x98 + 116, 0x100)
    b[0x188:0x190] = b'.rdata\0\0'
    put32(b, 0x188 + 8, 0x4000)
    put32(b, 0x188 + 12, 0x1000)
    put32(b, 0x188 + 16, 0x4000)
    put32(b, 0x188 + 20, 0x2000)
    struct.pack_into('<IIHHIIIIIII', b, 0x2000, 0, 0, 0, 0, 0x11c0, 1, 2, 2, 0x1100, 0x1120, 0x1140)
    struct.pack_into('<II', b, 0x2100, rva, 0x2900)
    struct.pack_into('<II', b, 0x2120, 0x1160, 0x1190)
    struct.pack_into('<HH', b, 0x2140, 0, 1)
    b[0x2160:0x2160 + 20] = b'RunningTrialControl\0'
    b[0x2190:0x2190 + 12] = b'OtherExport\0'
    # Overlay deliberately contains every byte value, including NUL and high bytes.
    for offset in range(24576, size):
        b[offset] = (offset * 37 + offset // 256) & 255
    return bytes(b)


def fixtures(root: Path) -> Path:
    root.mkdir()
    cases = []
    def case(name: str, data: bytes | None, expected: dict, export: str = 'RunningTrialControl') -> None:
        path = root / (name + '.dll')
        if data is not None:
            path.write_bytes(data)
        cases.append({"name": name, "path": str(path), "export": export, "expected": expected,
                      "sha256": digest(data) if data is not None else None})
    original = pe()
    case('valid-control', original, {"rva": 0x2800})
    case('valid-other-name', original, {"rva": 0x2900}, 'OtherExport')
    case('missing-export', original, {"error": 'export-not-found'}, 'MissingExport')
    case('forwarded-export', pe(rva=0x1010), {"error": 'forwarded-export-refused'})
    for size in [0, 1, 4095, 4096]:
        case(f'truncated-{size}', original[:size], {"error": 'dll-file'})
    for size in [4097, 5000, 8192]:
        case(f'truncated-safe-pe-bound-{size}', original[:size], {"error": 'pe-bound'})
    case('truncated-after-required-data', original[:12288], {"rva": 0x2800})
    case('missing-file', None, {"error": 'dll-file'})
    directory = root / 'directory-open.dll'
    directory.mkdir()
    cases.append({"name": 'directory-open', "path": str(directory), "export": 'RunningTrialControl',
                  "expected": {"error": 'dll-file'}})
    for name, offset, value in [('bad-dos', 0, 0), ('bad-nt', 0x80, 0), ('bad-optional', 0x98, 0)]:
        b = bytearray(original)
        if name == 'bad-optional':
            struct.pack_into('<H', b, offset, value)
        else:
            put32(b, offset, value)
        case(name, bytes(b), {"error": 'dll-pe'})
    for name, offset, value in [('export-unmapped', 0x98 + 112, 0x9000),
                                ('names-unmapped', 0x2020, 0x9000),
                                ('ordinal-unmapped', 0x2024, 0x9000),
                                ('functions-unmapped', 0x201c, 0x9000),
                                ('name-unmapped', 0x2120, 0x9000),
                                ('raw-data-outside-file', 0x188 + 20, 0x900000)]:
        b = bytearray(original)
        put32(b, offset, value)
        case(name, bytes(b), {"error": 'pe-bound' if 'outside' in name else 'pe-rva'})
    for size in [65535, 65536, 65537, 131071, 131072, 131073, 1048576]:
        case(f'block-boundary-{size}', pe(size), {"rva": 0x2800})
    case('路径 with spaces', original, {"rva": 0x2800})
    for i in range(48):
        size = 24576 + (i * 7919 % 180000)
        rva = 0x2800 + i * 16
        name = 'MissingExport' if i % 7 == 0 else 'RunningTrialControl'
        expected = {"error": 'export-not-found'} if i % 7 == 0 else {"rva": rva}
        case(f'deterministic-pe-{i:02d}', pe(size, rva), expected, name)
    manifest = root / 'manifest.json'
    write_json(manifest, cases)
    return manifest


def toolchain(args: argparse.Namespace) -> tuple[Path, dict, dict]:
    vc = Path(args.vc_tools) if args.vc_tools else Path(os.environ.get('VCToolsInstallDir',
        r'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC'))
    if not (vc / 'include').is_dir():
        vc = max((p for p in vc.iterdir() if (p / 'include').is_dir()), key=lambda p: p.name)
    sdk = Path(args.sdk_root)
    version = args.sdk_version or max(p.name for p in (sdk / 'Include').iterdir()
                                      if (p / 'um/Windows.h').exists())
    env = os.environ.copy()
    env['INCLUDE'] = ';'.join(str(p) for p in [vc / 'include', sdk / 'Include' / version / 'um',
                               sdk / 'Include' / version / 'shared', sdk / 'Include' / version / 'ucrt'])
    env['LIB'] = ';'.join(str(p) for p in [vc / 'lib/x64', sdk / 'Lib' / version / 'um/x64',
                                        sdk / 'Lib' / version / 'ucrt/x64'])
    env['PATH'] = str(vc / 'bin/Hostx64/x64') + os.pathsep + env.get('PATH', '')
    compiler = vc / 'bin/Hostx64/x64/cl.exe'
    return compiler, env, {"compiler": pin(compiler), "vcTools": str(vc), "sdkVersion": version,
        "filebufImplementation": pin(vc / 'include/__msvc_filebuf.hpp'),
        "streambufImplementation": pin(vc / 'include/streambuf')}


def run(command: list[str], root: Path, name: str, env: dict) -> subprocess.CompletedProcess:
    result = subprocess.run(command, cwd=root, env=env, capture_output=True, timeout=240)
    (root / (name + '.stdout')).write_bytes(result.stdout)
    (root / (name + '.stderr')).write_bytes(result.stderr)
    write_json(root / (name + '.command.json'), {"argv": command, "returnCode": result.returncode})
    return result


def summarize(samples: list[dict], variant: str) -> dict:
    rows = [r for r in samples if r['variant'] == variant]
    calls = sum(r['calls'] for r in rows)
    means = sorted(r['wallMs'] / r['calls'] for r in rows)
    return {"calls": calls, "threadCpuMs": sum(r['threadCpuMs'] for r in rows),
            "threadCpuMsPerCall": sum(r['threadCpuMs'] for r in rows) / calls,
            "wallMsPerCall": sum(r['wallMs'] for r in rows) / calls,
            "batchMeanWallP50Ms": statistics.median(means),
            "batchMeanWallMinMs": min(means), "batchMeanWallMaxMs": max(means),
            "outputsIdentical": len({(r['rva'], r['checksum']) for r in rows}) == 1}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--before', required=True, type=Path)
    parser.add_argument('--evidence', required=True, type=Path, help='new run subdirectory; must not exist')
    parser.add_argument('--own-dll', required=True, type=Path)
    parser.add_argument('--source', type=Path)
    parser.add_argument('--vc-tools')
    parser.add_argument('--sdk-root', default=r'C:/Program Files (x86)/Windows Kits/10')
    parser.add_argument('--sdk-version')
    parser.add_argument('--iterations', type=int, default=1024)
    parser.add_argument('--rounds', type=int, default=11)
    args = parser.parse_args()
    if args.iterations <= 0 or args.rounds < 3:
        parser.error('iterations must be positive and rounds at least 3')
    repo = Path(__file__).resolve().parents[1]
    source = (args.source or repo / 'native/steam_original_recovery_engine/driver_port.inc').resolve()
    root = args.evidence.resolve()
    root.mkdir(parents=True, exist_ok=False)
    try:
        old = args.before.read_bytes()
        new = source.read_bytes()
        (root / 'driver_port.before.inc').write_bytes(old)
        (root / 'driver_port.after.inc').write_bytes(new)
        a0, a1, old_fn, old_read = extract(old)
        b0, b1, new_fn, new_read = extract(new)
        assert old[:a0] == new[:b0] and old[a1:] == new[b1:], 'bytes outside exportRva changed'
        assert old_fn[old_fn.index(MARKER):] == new_fn[new_fn.index(MARKER):], 'PE parser/guards changed'
        assert old_fn.splitlines()[0] == new_fn.splitlines()[0], 'function signature changed'
        assert old_read != new_read, 'no reader difference'
        assert old_read.count(b'std::ifstream') == new_read.count(b'std::ifstream') == 1
        assert new_read.count(b'if (count < blockSize)') == 1, 'expected reviewed short-read stopping rule'
        extraction = {}
        for label, fn, read in [('old', old_fn, old_read), ('new', new_fn, new_read)]:
            (root / (label + '.export.inc')).write_bytes(fn + b'\n')
            reader = b'std::vector<unsigned char> readBytes(const std::wstring &file) {\n' + read + b'  return b;\n}\n'
            (root / (label + '.reader.inc')).write_bytes(reader)
            (root / (label + '.fault.export.inc')).write_bytes(fn.replace(b'std::ifstream', b'FixtureInput') + b'\n')
            (root / (label + '.fault.reader.inc')).write_bytes(reader.replace(b'std::ifstream', b'FixtureInput'))
            extraction[label] = {"functionSha256": digest(fn), "readerSha256": digest(read),
                                 "functionByteRange": [a0, a1] if label == 'old' else [b0, b1]}
        retry = (root / 'new.fault.reader.inc').read_bytes().replace(b'if (count < blockSize)', b'if (count == 0)')
        (root / 'retry.fault.reader.inc').write_bytes(retry)
        first = (root / 'new.reader.inc').read_bytes().replace(b'if (count < blockSize)', b'if (count <= blockSize)')
        (root / 'first.reader.inc').write_bytes(first)
        patch = ''.join(difflib.unified_diff(old.decode('utf-8').splitlines(True), new.decode('utf-8').splitlines(True),
                          fromfile='before/driver_port.inc', tofile='after/driver_port.inc'))
        (root / 'driver_port.patch').write_text(patch, encoding='utf-8', newline='')
        own_before = pin(args.own_dll)
        manifest = fixtures(root / 'fixtures')
        compiler, env, tools = toolchain(args)
        env['TMP'] = env['TEMP'] = str(root)
        template = Path(__file__).resolve().parent / 'steam_original_recovery_export_reader_fixtures/harness.cpp'
        harness = root / 'harness.cpp'
        harness.write_bytes(template.read_bytes())
        provenance = {"sourceBefore": pin(args.before), "sourceAfter": pin(source),
            "selftest": pin(Path(__file__)), "harnessTemplate": pin(template), "extraction": extraction,
            "outsideFunctionBytesEqual": True, "parserAndFailureRuleBytesEqual": True,
            "inputTypeSubstitutionForFaultCopiesOnly": 'std::ifstream -> FixtureInput, exactly once',
            "readerOnlyCopies": 'verbatim function statements before first dll-file need; return b appended',
            "toolchain": tools, "jsonHeader": pin(repo / 'native/steam_original_recovery_engine/vendor/json.hpp'),
            "ownDllBefore": own_before, "wholeEngineCompiled": False, "dllLoads": 0,
            "steamProcessReads": 0, "applicationActions": 0}
        write_json(root / 'provenance.json', provenance)
        exe = root / 'export-reader-selftest.exe'
        command = [str(compiler), '/nologo', '/std:c++20', '/utf-8', '/EHsc', '/MT', '/O2', '/W4', '/WX',
                   '/I' + str(root), '/I' + str(repo / 'native/steam_original_recovery_engine/vendor'),
                   str(harness), '/Fo' + str(root / 'harness.obj'), '/Fe' + str(exe), '/link', '/INCREMENTAL:NO']
        build = run(command, root, 'build', env)
        if build.returncode:
            raise RuntimeError('isolated fixture compile failed; build.stdout/build.stderr retained')
        test = run([str(exe), 'test', str(manifest), str(args.own_dll.resolve())], root, 'differential', env)
        if test.stdout:
            result = json.loads(test.stdout)
            write_json(root / 'differential.json', result)
        if test.returncode or not result['success']:
            raise RuntimeError('differential tests failed; original results retained')
        bench = run([str(exe), 'bench', str(args.own_dll.resolve()), str(args.iterations), str(args.rounds)],
                    root, 'benchmark', env)
        if bench.returncode:
            raise RuntimeError('benchmark failed; raw output retained')
        measurement = json.loads(bench.stdout)
        write_json(root / 'benchmark.json', measurement)
        old_summary = summarize(measurement['samples'], 'old')
        new_summary = summarize(measurement['samples'], 'new')
        own_after = pin(args.own_dll)
        assert own_before == own_after, 'fixed own DLL changed'
        assert source.read_bytes() == new, 'source changed during selftest'
        assert len({s['rva'] for s in measurement['samples']}) == 1
        summary = {"success": True, "tests": {"passed": result['passed'], "total": result['total']},
                   "old": old_summary, "new": new_summary,
                   "wallReductionPercent": (1 - new_summary['wallMsPerCall'] / old_summary['wallMsPerCall']) * 100,
                   "threadCpuReductionPercent": (1 - new_summary['threadCpuMsPerCall'] / old_summary['threadCpuMsPerCall']) * 100,
                   "ownDllBefore": own_before, "ownDllAfter": own_after, "exe": pin(exe),
                   "scope": 'warm-cache extracted exportRva open/read/parse/close microbenchmark only',
                   "limits": ['No complete recovery speedup claim or end-to-end validation.',
                              'Unchanged PE parser is not fully bounds-safe; fixtures avoid preexisting undefined accesses.',
                              'No new atomic file snapshot guarantee for concurrent writers.',
                              'Allocation failure timing and machine-specific performance are not equivalence claims.']}
        write_json(root / 'summary.json', summary)
        print(json.dumps(summary, indent=2))
        return 0
    except Exception as error:
        write_json(root / 'failure.json', {"error": str(error), "traceback": traceback.format_exc()})
        print(f'{error}; evidence: {root}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
