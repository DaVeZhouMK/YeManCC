"""Directed extension of replay.py; all artifacts stay in its supplied scratch.

No product process or hardware is launched. This compiles actual core/helper
sources, a source-generated oracle and terminal adapters only when the caller
explicitly runs the original replay/verify entry with --case.
"""
import ctypes
import hashlib
import json
import os
import re
import pathlib
import subprocess
import sys
import shutil
import time
import uuid
import xml.etree.ElementTree as ET
from extract_pinned import FILES as ORIGINAL_PINNED_FILES
from x2_source import generate_oracle, generate_private_owner, verify_sources, X2_SOURCE

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
FROZEN = {
    'FanLab/LightSetter/Oxp3OwnerLease.cs': '14db73d31916a015cee42e6a65f42cb55696fbb762044aa1c1586e9456b6e452',
    'native/oxp3_lighting_owner.h': '6fb2e7b6b408c89c784077c057212fe321a79079878f963e549b74ea4ff9434e',
    'FanLab/LightSetter/DeviceCore/PinnedX2MiniProProtocol.cs': '49257929a8d670c6c823be78e7695410b9606909d4419100bd92287f580cd330',
    'FanLab/LightSetter/DeviceCore/X2MiniProDeviceCore.cs': '602d14ce95f00a6f85972dbdfcb2816b7bfb3a2750a989be46b960fd3619ebb5',
    'FanLab/LightSetter/DeviceCore/WindowsHidTransport.cs': 'a6acaacca7d18ff57788d77c2379555d153ac2dffca322772cf0326f468c89bf',
    'FanLab/LightSetter/DeviceCore/LightingDeviceCore.cs': '1a369735af086c140bbea47ab378a31b6a7d82ca9139e0b89fe6d28f8736f9af',
    'FanLab/LightSetter/DeviceCoreConsumer.cs': '09949446e599c3e53f9d528276cb77366e9e14b861067caadaeb4af9353ac9e6',
    'FanLab/LightSetter/Program.cs': '37dc79a495179a07feef24ab79f4172b8979e5131cea004548efd9144ff1306b',
    'FanLab/LightSetter/DeviceCore/SOURCE-NOTICE.md': '58fb0a275dc3f979c5a3388d6a4086d492e4d6798c45716564fd8ed1c550ec44',
}
SELECTORS = ('x2mini', 'x2mini-wire', 'x2mini-admission', 'x2mini-terminal', 'x2mini-negative-controls')


def sha(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')


def adapt_transport(out):
    source = ROOT / 'FanLab/LightSetter/DeviceCore/WindowsHidTransport.cs'
    text = source.read_text(encoding='utf-8-sig').replace('\r\n', '\n')
    start = text.index('    private static class Native\n')
    native = text[start:]
    structs = native[native.index('        [StructLayout'):native.index('        [DllImport')]
    # Preserve the complete production class through Failure(), including all
    # admission checks. Replace only the native declarations and stream port.
    prefix = text[:start].replace('FileStream', 'FixtureFileStream').replace('File.OpenHandle(', 'TransportProbe.OpenHandle(')
    terminal = '''#pragma warning disable CS0649 // untouched native struct fields at the injected terminal
    private static class Native {
'''+structs+'''
        internal static void HidD_GetHidGuid(out Guid guid) { guid=Guid.Empty; throw new InvalidOperationException("offline enumeration forbidden"); }
        internal static bool HidD_GetAttributes(SafeFileHandle handle,ref HidAttributes attributes) {
            var ok=TransportProbe.FillAttributes(out var vid,out var pid); attributes.VendorId=vid; attributes.ProductId=pid; return ok;
        }
        internal static bool HidD_GetPreparsedData(SafeFileHandle handle,out IntPtr data) {
            data=(IntPtr)107; if(!TransportProbe.PreparsedOk)return false; TransportProbe.Preparsed++; return true;
        }
        internal static bool HidD_FreePreparsedData(IntPtr data) { TransportProbe.Freed++; return true; }
        internal static int HidP_GetCaps(IntPtr data,out HidCaps caps) {
            caps=new HidCaps { Usage=TransportProbe.Usage,UsagePage=TransportProbe.Page,OutputReportByteLength=TransportProbe.Length,Reserved=new ushort[17] };
            return TransportProbe.CapsOk?0x00110000:0;
        }
        internal static SafeFileHandle CreateFile(string path,uint access,uint share,IntPtr security,uint creation,uint flags,IntPtr template) => TransportProbe.Probe(path);
        internal static DeviceInfoSet SetupDiGetClassDevs(ref Guid guid,string? enumerator,IntPtr parent,uint flags) => throw new InvalidOperationException("offline enumeration forbidden");
        internal static bool SetupDiEnumDeviceInterfaces(DeviceInfoSet devices,IntPtr data,ref Guid guid,uint index,ref DeviceInterfaceData info) => throw new InvalidOperationException("offline enumeration forbidden");
        internal static bool SetupDiGetDeviceInterfaceDetail(DeviceInfoSet devices,ref DeviceInterfaceData info,IntPtr detail,uint size,out uint required,IntPtr data) { required=0; throw new InvalidOperationException("offline enumeration forbidden"); }
        private static bool SetupDiDestroyDeviceInfoList(IntPtr devices) => true;
    }
#pragma warning restore CS0649
}
'''
    generated = out / 'ActualWindowsHidTransport.g.cs'
    generated.write_text('#nullable enable\n' + prefix + terminal, encoding='utf-8', newline='\n')
    write_json(out / 'transport-source.json', {
        'source': str(source), 'sha256': sha(source), 'generated': str(generated), 'generatedSha256': sha(generated),
        'adaptations': ['Native P/Invoke terminals replaced with memory probes; enumeration throws',
                        'FileStream type bound to fixture stream; File.OpenHandle bound to fixture terminal'],
        'actualLogicUnchanged': ['OpenX2MiniPro', 'IsVendorHid', 'Write', 'Dispose'],
        'hardwareDescriptorVerified': False, 'shortWriteMeaning': 'a partial terminal write throws; FileStream.WriteAsync has full-or-exception contract, no byte-count return'})
    return generated


def input_closure(selector):
    # Exact source/derivation inputs of this selection; default legacy, native
    # packet replay and README files are not silently added to its compile set.
    files = {HERE / name for name in ('replay.py', 'verify.ps1', 'x2_replay.py',
                                     'x2_source.py', 'extract_pinned.py', 'source-lock.json')}
    files.update((ROOT / f).resolve() for f in FROZEN)
    projects = []
    if selector != 'x2mini-terminal':
        projects.extend(('ReplayCore.csproj', 'ReplayConsumer.csproj'))
        files.add(HERE / 'X2MiniReplayEntry.cs')
        files.add(ROOT / 'capabilities/hc-lighting-policy.json')
    if selector in ('x2mini', 'x2mini-terminal'):
        projects.append('TransportReplay.csproj')
    for project in projects:
        files.add(HERE / project)
        for item in ET.parse(HERE / project).iter('Compile'):
            value = item.attrib.get('Include', '')
            if value and '$(' not in value:
                files.add((HERE / value).resolve())
    for name in ('global.json', 'Directory.Build.props', 'Directory.Build.targets', 'NuGet.Config', 'nuget.config'):
        if (ROOT / name).is_file():
            files.add(ROOT / name)
    files.update(pathlib.Path(X2_SOURCE['sourceRoot']) / f for f in X2_SOURCE['files'])
    files.update(ROOT / 'deps/handheldcompanion-runtime/source' / f for f in ORIGINAL_PINNED_FILES)
    files.add(ROOT / 'FanLab/LightSetter/DeviceCore/PinnedOneXHidV1.g.cs')
    return {str(p.resolve()): sha(p) for p in sorted(files)}


def read_json(path):
    return json.loads(pathlib.Path(path).read_text(encoding='utf-8-sig'))


def bind_dotnet():
    # Match the repository-pinned SDK's actual framework, without changing a
    # compiled runtimeconfig, production pin or the application roll-forward policy.
    executable = pathlib.Path(shutil.which('dotnet') or '').resolve()
    sdk_version = read_json(ROOT / 'global.json')['sdk']['version']
    if not re.fullmatch(r'\d+\.\d+\.\d+', sdk_version) or not executable.is_file():
        raise RuntimeError('fixed SDK/exact dotnet executable required')
    sdk = executable.parent / 'sdk' / sdk_version
    config = sdk / 'dotnet.runtimeconfig.json'
    framework = read_json(config)['runtimeOptions']['framework']
    version = framework['version']
    if framework['name'] != 'Microsoft.NETCore.App' or not re.fullmatch(r'10\.0\.\d+', version):
        raise RuntimeError('fixed SDK actual .NET 10 framework required')
    runtime = executable.parent / 'shared' / framework['name'] / version
    if not (runtime / 'coreclr.dll').is_file():
        raise RuntimeError('fixed SDK runtime is not installed: ' + version)
    return {'executable': str(executable), 'executableSha256': sha(executable),
            'sdkVersion': sdk_version, 'sdkRuntimeConfig': str(config),
            'sdkRuntimeConfigSha256': sha(config), 'framework': framework,
            'runtimeRoot': str(runtime), 'rollForward': 'Disable',
            'basis': 'repository global.json -> actual SDK dotnet.runtimeconfig.json'}


def regular_file(path):
    path = pathlib.Path(path)
    if not path.is_file() or any(p.is_symlink() or (hasattr(p, 'is_junction') and p.is_junction())
                                 for p in (path, *path.parents)):
        raise RuntimeError('resume input must be a regular file without redirected ancestors: ' + str(path))
    return path


def admit_launch_recovery(manifest_file, expected_hash, scratch, selector, binding):
    # Only the observed pre-main framework failure shape is admitted; the pinned
    # receipt identifies its attempt. No round directory is a source dependency.
    manifest_file = regular_file(pathlib.Path(manifest_file).resolve())
    if not re.fullmatch(r'[a-f0-9]{64}', expected_hash or '') or sha(manifest_file) != expected_hash:
        raise RuntimeError('resume admission receipt SHA mismatch')
    admission = read_json(manifest_file)
    if admission.get('kind') != 'ymcc-x2mini-pre-main-launch-recovery-v1' or selector != 'x2mini' or admission.get('selector') != selector:
        raise RuntimeError('only a pinned x2mini pre-main launch recovery is admitted')
    prior = pathlib.Path(admission['priorDirectory']).resolve()
    if prior.name != 'x2mini' or prior == ROOT or ROOT in prior.parents or prior == scratch or prior in scratch.parents or scratch in prior.parents:
        raise RuntimeError('invalid prior attempt or recovery output directory')
    if admission['sourceRoot'] != str(ROOT) or admission['runtimeBinding'] != binding:
        raise RuntimeError('source root or exact SDK/runtime binding changed')
    # Preserve every captured file and reject additional build/dependency members.
    for root_name, expected_members in admission['directoryMembers'].items():
        directory = pathlib.Path(root_name)
        actual = sorted(str(p.relative_to(directory)).replace('\\', '/') for p in directory.rglob('*') if p.is_file())
        if actual != expected_members:
            raise RuntimeError('resume directory member set changed: ' + root_name)
    for name, expected in admission['fileHashes'].items():
        if sha(regular_file(name)) != expected:
            raise RuntimeError('resume source/dependency/tool/artifact changed: ' + name)
    old = read_json(prior / 'result.json')
    before = read_json(prior / 'inputs-before.json')
    if read_json(prior / 'inputs-after.json') != before or old.get('sourceDrift') != []:
        raise RuntimeError('prior source closure was not stable')
    if old.get('selector') != selector or old.get('status') != 'FAIL' or old.get('defaultExecuted') is not False or old.get('selected') != [] or old.get('actual') != [] or old.get('tests') != []:
        raise RuntimeError('prior attempt is not an unexecuted exact launch failure')
    steps = old.get('steps', [])
    if [step.get('name') for step in steps] != ['original-source-check', 'actual-core', 'actual-helper', 'managed-execute'] or [step.get('exitCode') for step in steps] != [0, 0, 0, 2147516566]:
        raise RuntimeError('both original builds must be successful and followed only by the observed host launch failure')
    if steps != admission['priorSteps']:
        raise RuntimeError('original arguments/tool/build receipts changed')
    if (prior / 'managed-execute.stdout.log').stat().st_size or (prior / 'events.jsonl').exists() or (prior / 'events.jsonl.selection.json').exists():
        raise RuntimeError('prior managed scenarios may already have executed')
    current = input_closure(selector)
    delta = admission['runnerDelta']
    expected_delta = {str(HERE / 'replay.py'), str(HERE / 'x2_replay.py')}
    correction = admission.get('fixtureCorrection')
    if correction is not None:
        kind = correction.get('kind')
        if kind not in ('direct-core-context-isolation-v1', 'missing-owner-typed-diagnostic-v1') or correction.get('file') != str(HERE / 'X2MiniReplayEntry.cs'):
            raise RuntimeError('unsupported consumer fixture correction')
        for key in ('failedConsumerOutput', 'failedConsumerResult'):
            if correction[key] not in admission['fileHashes']:
                raise RuntimeError('fixture failure receipt is not pinned')
        failed = read_json(correction['failedConsumerOutput'])
        failed_result = read_json(correction['failedConsumerResult'])
        cases = failed.get('cases', [])
        if failed.get('ok') is not False or not cases or failed_result.get('sourceDrift') != [] or any(c.get('status') != 'PASS' for c in cases[:-1]) or cases[-1].get('status') != 'FAIL':
            raise RuntimeError('fixture correction requires a pinned partial failure with exact completed cases')
        operations = cases[-1].get('operations', [])
        if len(operations) != 1 or operations[0].get('events') != []:
            raise RuntimeError('fixture failure must precede all helper writes')
        if kind == 'direct-core-context-isolation-v1':
            expected_cases = ['matrix-same-assembly-model-operation-admission', 'core-five-zone-secondary-solid', 'core-64-byte-builder-and-65-byte-report-boundaries', 'helper-solid-secondary-and-brightness']
            if [c.get('name') for c in cases] != expected_cases or 'LOADED_ORIGIN_UNVERIFIED' not in operations[0].get('raw', ''):
                raise RuntimeError('source-origin correction failure differs')
        else:
            if len(cases) != 52 or cases[-1].get('name') != 'helper-preopen-no-current-owner-no-discovery' or [c.get('name') for c in cases] != failed['selected'][:len(cases)]:
                raise RuntimeError('missing-owner correction exact prior selection differs')
            diagnostic = json.loads(operations[0]['raw'])
            failures = diagnostic.get('dependencyFailures', [])
            if diagnostic.get('ok') is not False or not any(f.get('ExceptionType') == 'System.IO.FileNotFoundException' and f.get('HResultHex') == '0x80070002' and f.get('TargetSite') == 'System.IO.MemoryMappedFiles.MemoryMappedFile.OpenExisting' for f in failures):
                raise RuntimeError('missing-owner typed failure differs')
            if correction.get('remainingCases') != failed['selected'][len(cases)-1:]:
                raise RuntimeError('continuation may execute only the failed and unexecuted original cases')
        expected_delta.add(str(HERE / 'X2MiniReplayEntry.cs'))
    if set(delta) != expected_delta or set(before) != set(current):
        raise RuntimeError('resume source delta differs from its authorized launcher/fixture correction')
    for name, expected in before.items():
        if name in delta:
            if delta[name] != {'beforeSha256': expected, 'afterSha256': current[name]}:
                raise RuntimeError('unapproved launcher edit: ' + name)
        elif current[name] != expected:
            raise RuntimeError('original source closure changed: ' + name)
    owner = read_json(prior / 'owner-source.json')
    if not re.fullmatch(r'YMCC-X2Replay-[a-f0-9]{32}', owner.get('privateScope', '')) or owner.get('fixedProductObjectsOpenedOrWritten') is not False or sha(prior / 'PrivateOwnerLease.cs') != owner['generatedSha256']:
        raise RuntimeError('original private owner provenance changed')
    core = prior / 'build/actual-core/bin/ReplayCore/release/YeManLightDeviceCore.dll'
    consumer = prior / 'build/actual-helper/bin/ReplayConsumer/release_win-x64/LightSetterDeviceCoreConsumerReplay.dll'
    if sha(core) != admission['coreSha256'] or sha(consumer) != admission['helperSha256'] or sha(core) not in (prior / 'ReplayPins.cs').read_text(encoding='utf-8-sig'):
        raise RuntimeError('original compiled core/helper/scratch pin mismatch')
    if pathlib.Path(steps[1]['tool']['path']).resolve() != pathlib.Path(binding['executable']) or steps[1]['tool']['sha256'] != binding['executableSha256'] or steps[2]['tool'] != steps[1]['tool'] or steps[3]['tool'] != steps[1]['tool']:
        raise RuntimeError('original dotnet tool identity changed')
    if pathlib.Path(steps[0]['tool']['path']).resolve() != pathlib.Path(sys.executable).resolve() or steps[0]['tool']['sha256'] != sha(sys.executable):
        raise RuntimeError('original Python interpreter changed')
    # The two outer attempts already include their inner producer time. Do not
    # add nested 3.579s a second time or refund the policy-rejected first call.
    historical = sum(read_json(row['path'])['elapsedMs'] for row in admission['outerAttempts']) / 1000
    if abs(historical - admission['historicalChargedSeconds']) > 0.000001 or historical < old['elapsedSeconds'] or not 0 < historical < 240:
        raise RuntimeError('invalid historical cumulative budget')
    claim = manifest_file.parent / ('resume-' + expected_hash + '.claimed.json')
    return {'manifest': str(manifest_file), 'manifestSha256': expected_hash,
            'priorDirectory': str(prior), 'core': str(core), 'helper': str(consumer),
            'historicalChargedSeconds': historical, 'claim': str(claim),
            'reusedBuilds': [steps[1]] if correction else [steps[1], steps[2]],
            'rebuildHelperForFixtureCorrection': bool(correction),
            'fixtureCorrection': correction,
            'remainingCases': correction.get('remainingCases') if correction else None, 'priorResultSha256': sha(prior / 'result.json'),
            'noPriorRuntimeCases': True, 'artifactReuseIsNotRuntimePass': True,
            'dependencyFilesVerified': len(admission['fileHashes'])}


class OwnedJob:
    """Bound scratch children; closing a Windows job returns its process tree."""
    def __init__(self):
        if os.name != 'nt':
            raise RuntimeError('the actual helper/owner fixture requires Windows')
        from ctypes import wintypes as w
        class Basic(ctypes.Structure):
            _fields_ = [('PerProcessUserTimeLimit', ctypes.c_longlong), ('PerJobUserTimeLimit', ctypes.c_longlong),
                        ('LimitFlags', w.DWORD), ('MinimumWorkingSetSize', ctypes.c_size_t), ('MaximumWorkingSetSize', ctypes.c_size_t),
                        ('ActiveProcessLimit', w.DWORD), ('Affinity', ctypes.c_size_t), ('PriorityClass', w.DWORD), ('SchedulingClass', w.DWORD)]
        class IO(ctypes.Structure):
            _fields_ = [(n, ctypes.c_ulonglong) for n in ('ReadOperationCount','WriteOperationCount','OtherOperationCount','ReadTransferCount','WriteTransferCount','OtherTransferCount')]
        class Extended(ctypes.Structure):
            _fields_ = [('BasicLimitInformation', Basic), ('IoInfo', IO), ('ProcessMemoryLimit', ctypes.c_size_t),
                        ('JobMemoryLimit', ctypes.c_size_t), ('PeakProcessMemoryUsed', ctypes.c_size_t), ('PeakJobMemoryUsed', ctypes.c_size_t)]
        self.api = ctypes.WinDLL('kernel32', use_last_error=True)
        self.api.CreateJobObjectW.restype = w.HANDLE
        self.api.CreateJobObjectW.argtypes = [ctypes.c_void_p, w.LPCWSTR]
        self.api.SetInformationJobObject.argtypes = [w.HANDLE, ctypes.c_int, ctypes.c_void_p, w.DWORD]
        self.api.AssignProcessToJobObject.argtypes = [w.HANDLE, w.HANDLE]
        self.api.TerminateJobObject.argtypes = [w.HANDLE, w.UINT]
        self.api.CloseHandle.argtypes = [w.HANDLE]
        self.handle = self.api.CreateJobObjectW(None, None)
        info = Extended(); info.BasicLimitInformation.LimitFlags = 0x2000
        if not self.handle or not self.api.SetInformationJobObject(self.handle, 9, ctypes.byref(info), ctypes.sizeof(info)):
            error = ctypes.get_last_error()
            if self.handle:
                self.api.CloseHandle(self.handle); self.handle = None
            raise ctypes.WinError(error)
    def assign(self, child):
        if not self.api.AssignProcessToJobObject(self.handle, int(child._handle)):
            error = ctypes.get_last_error()
            child.kill(); child.wait()
            raise ctypes.WinError(error)
    def close(self):
        if self.handle:
            self.api.TerminateJobObject(self.handle, 124)
            self.api.CloseHandle(self.handle); self.handle = None


def run_directed(scratch, selector, resume_from=None, resume_sha256=None):
    started = time.monotonic()
    if bool(resume_from) != bool(resume_sha256) or (resume_from is not None and selector != 'x2mini'):
        raise ValueError('resume requires one pinned admission receipt and the exact x2mini selector')
    if selector not in SELECTORS:
        raise ValueError('unknown or empty --case')
    if scratch == ROOT or ROOT in scratch.parents:
        raise ValueError('scratch must be outside the source repository')
    if resume_from is not None:
        manifest = regular_file(pathlib.Path(resume_from).resolve())
        if not re.fullmatch(r'[a-f0-9]{64}', resume_sha256 or '') or sha(manifest) != resume_sha256:
            raise ValueError('resume admission receipt SHA mismatch')
        prior_evidence = pathlib.Path(read_json(manifest)['priorDirectory']).resolve().parent.parent
        if prior_evidence not in scratch.parents or any(p.is_symlink() or (hasattr(p, 'is_junction') and p.is_junction()) for p in (scratch, *scratch.parents)):
            raise ValueError('recovery scratch must stay in the original evidence tree, outside product directories')
    # One attempt owns one directory and one 240s budget. Existing/failed raw
    # evidence cannot be silently overwritten by another selector or retry.
    out = scratch / 'x2mini'
    out.mkdir(parents=True, exist_ok=False)
    deadline = started + 240
    steps = []; results = []; planned = []; job = None; before = None; recovery = None; binding = None; historical = 0.0
    summary = {'status': 'NOT_RUN', 'selector': selector, 'defaultExecuted': False,
               'tests': [], 'hardwareIo': False, 'physicalEffectVerified': False, 'limitSeconds': 240}
    def invoke(command, name, expected=0):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError('existing 240s replay budget exhausted')
        requested_command = [str(a) for a in command]
        command = requested_command.copy()
        if pathlib.Path(command[0]).name.lower() in ('dotnet', 'dotnet.exe'):
            command[0] = binding['executable']
            if len(command) > 1 and command[1].lower().endswith('.dll'):
                runtimeconfig = read_json(pathlib.Path(command[1]).with_suffix('.runtimeconfig.json'))
                framework = runtimeconfig['runtimeOptions'].get('framework', {})
                if framework.get('name') != binding['framework']['name'] or not framework.get('version', '').startswith('10.0.') or 'frameworks' in runtimeconfig['runtimeOptions']:
                    raise RuntimeError('only the BCL-only .NET 10 actual fixture may use this binding')
                command[1:1] = ['exec', '--fx-version', binding['framework']['version'], '--roll-forward', 'Disable']
        executable = pathlib.Path(shutil.which(str(command[0])) or str(command[0])).resolve()
        tool_identity = {'path': str(executable), 'sha256': sha(executable)}
        at = time.monotonic()
        child = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                 encoding='utf-8', errors='replace', creationflags=subprocess.CREATE_NO_WINDOW,
                                 env={**os.environ, 'DOTNET_ROOT': str(pathlib.Path(binding['executable']).parent), 'DOTNET_MULTILEVEL_LOOKUP': '0', 'DOTNET_ROLL_FORWARD': 'Disable', 'DOTNET_CLI_TELEMETRY_OPTOUT': '1', 'PYTHONDONTWRITEBYTECODE': '1', 'YMCC_LIGHT_TRANSPORT_SELECTION_LOG': str(out / 'transport-selection.json')})
        row = {'argv': [str(a) for a in command], 'tool': tool_identity, 'pid': child.pid, 'timeoutSeconds': remaining, 'name': name, 'requestedArgv': requested_command, 'runtimeBinding': binding}
        steps.append(row)
        try:
            job.assign(child)
            stdout, stderr = child.communicate(timeout=remaining)
        except subprocess.TimeoutExpired:
            job.close(); stdout, stderr = child.communicate()
            row['timedOut'] = True
            raise TimeoutError('replay budget exhausted in ' + name)
        finally:
            row['elapsedSeconds'] = time.monotonic() - at
            row['exitCode'] = child.returncode
            if 'stdout' in locals():
                (out / (name + '.stdout.log')).write_text(stdout, encoding='utf-8')
                (out / (name + '.stderr.log')).write_text(stderr, encoding='utf-8')
        if child.returncode != expected:
            raise RuntimeError(f'{name} exit {child.returncode}, expected {expected}; raw log preserved')
        return stdout
    def build(project, area, props=()):
        artifacts = out / 'build' / area
        invoke(['dotnet', 'build', str(HERE / project), '-c', 'Release', '--artifacts-path', str(artifacts),
                '--nologo', '-v:q', '-p:UseSharedCompilation=false', '-nodeReuse:false', *props], area)
        return artifacts
    def consume(raw, name, allowed_groups):
        result = json.loads(raw)
        if name == 'managed-result':
            plan = json.loads((out / 'events.jsonl.selection.json').read_text(encoding='utf-8-sig'))
            if plan['selector'] != result['selector'] or plan['selected'] != result['selected']:
                raise AssertionError('pre-execution selection differs from actual receipt')
        if name == 'transport-result':
            plan = json.loads((out / 'transport-selection.json').read_text(encoding='utf-8-sig'))
            if plan['selected'] != result['selected']:
                raise AssertionError('transport pre-execution selection differs from receipt')
        selected = result['selected']; actual = [c['name'] for c in result['cases']]
        if not selected or len(selected) != len(set(selected)) or selected != actual or not result['ok']:
            raise AssertionError(name + ': actual scenarios differ from selected exact set')
        if any(c['status'] != 'PASS' or c.get('group') not in allowed_groups for c in result['cases']):
            raise AssertionError(name + ': unselected group or failed case')
        planned.extend(selected); results.extend(result['cases'])
        write_json(out / (name + '.json'), result)
    try:
        binding = bind_dotnet()
        if resume_from is not None:
            recovery = admit_launch_recovery(resume_from, resume_sha256, scratch, selector, binding)
            historical = recovery['historicalChargedSeconds']
            deadline = started + 240 - historical
            if time.monotonic() >= deadline:
                raise TimeoutError('original cumulative 240s budget exhausted during admission')
            with pathlib.Path(recovery['claim']).open('x', encoding='utf-8') as claim:
                json.dump({'manifestSha256': resume_sha256, 'scratch': str(scratch), 'priorResultSha256': recovery['priorResultSha256'], 'historicalChargedSeconds': historical}, claim, indent=2)
            write_json(out / 'resume-admission.json', recovery)
        verify_sources()
        for relative, expected in FROZEN.items():
            if sha(ROOT / relative) != expected:
                raise RuntimeError('Kepler frozen source drift: ' + relative)
        before = input_closure(selector); write_json(out / 'inputs-before.json', before)
        # The original X1 generated source/lock constraints still execute. No
        # source lock, production pin or golden is rewritten.
        job = OwnedJob()
        if recovery is None:
            invoke([sys.executable, str(HERE / 'extract_pinned.py'), '--check'], 'original-source-check')
        need_managed = selector != 'x2mini-terminal'
        if need_managed:
            if recovery is not None:
                core = pathlib.Path(recovery['core']); consumer = pathlib.Path(recovery['helper'])
                if recovery.get('rebuildHelperForFixtureCorrection'):
                    previous = pathlib.Path(recovery['priorDirectory'])
                    consumer_artifacts = build('ReplayConsumer.csproj', 'actual-helper', [
                        '-p:ReplayPinSource=' + str(previous / 'ReplayPins.cs'),
                        '-p:ReplayOwnerSource=' + str(previous / 'PrivateOwnerLease.cs'),
                        '-p:ReplayEntrySource=' + str(HERE / 'X2MiniReplayEntry.cs'),
                        '-p:X2MiniOracleSource=' + str(previous / 'FixedSourceOracle.g.cs'), '-p:StartupObject=X2MiniReplayEntry'])
                    consumer = consumer_artifacts / 'bin/ReplayConsumer/release_win-x64/LightSetterDeviceCoreConsumerReplay.dll'
            else:
                oracle = out / 'FixedSourceOracle.g.cs'; generate_oracle(oracle)
                ipc = 'YMCC-X2Replay-' + uuid.uuid4().hex
                owner_generated = generate_private_owner(out, ipc)
                artifacts = build('ReplayCore.csproj', 'actual-core')
                core = artifacts / 'bin/ReplayCore/release/YeManLightDeviceCore.dll'
                pins = out / 'ReplayPins.cs'
                pins.write_text('internal static class LightingAssemblyPins { internal const string StandaloneLightingSha256 = "' + sha(core) + '"; }\n', encoding='utf-8')
                consumer_artifacts = build('ReplayConsumer.csproj', 'actual-helper', [
                    '-p:ReplayPinSource=' + str(pins), '-p:ReplayOwnerSource=' + str(owner_generated),
                    '-p:ReplayEntrySource=' + str(HERE / 'X2MiniReplayEntry.cs'), '-p:X2MiniOracleSource=' + str(oracle), '-p:StartupObject=X2MiniReplayEntry'])
                consumer = consumer_artifacts / 'bin/ReplayConsumer/release_win-x64/LightSetterDeviceCoreConsumerReplay.dll'
            if selector != 'x2mini-negative-controls':
                selected = selector if selector in ('x2mini-wire', 'x2mini-admission') else 'x2mini'
                managed_args = ['dotnet', str(consumer), '--case', selected, '--core', str(core), '--log', str(out / 'events.jsonl')]
                if recovery is not None and recovery.get('remainingCases'):
                    managed_args += ['--cases', ','.join(recovery['remainingCases'])]
                raw = invoke(managed_args, 'managed-execute')
                consume(raw, 'managed-result', {'wire', 'admission'} if selected == 'x2mini' else {selected.split('-')[1]})
            if selector in ('x2mini', 'x2mini-negative-controls'):
                protocol = (ROOT / 'FanLab/LightSetter/DeviceCore/PinnedX2MiniProProtocol.cs').read_text(encoding='utf-8-sig')
                mutants = {
                    'old-side0': ('new byte[] { 0x01, 0x02, 0x07 }', 'new byte[] { 0x00 }'),
                    'drop-secondary': ('SendZoneColor(secondaryColor, side, breathing)', 'SendZoneColor(mainColor, side, breathing)'),
                    'missing-zone': ('new byte[] { 0x01, 0x02, 0x07 }', 'new byte[] { 0x01, 0x02 }'),
                    'wrong-rgb-order': ('command.Add(color.R);\n            command.Add(color.G);\n            command.Add(color.B);', 'command.Add(color.B);\n            command.Add(color.G);\n            command.Add(color.R);'),
                }
                for name, (old, new) in mutants.items():
                    if protocol.count(old) != 1:
                        raise RuntimeError('semantic control seam drift: ' + name)
                    mutated = out / (name + '.cs'); mutated.write_text(protocol.replace(old, new), encoding='utf-8')
                    area = build('ReplayCore.csproj', 'control-' + name, ['-p:ReplayX2ProtocolSource=' + str(mutated)])
                    mutant_core = area / 'bin/ReplayCore/release/YeManLightDeviceCore.dll'
                    raw = invoke(['dotnet', str(consumer), '--case', 'x2mini-control', '--core', str(mutant_core), '--log', str(out / (name + '.events.jsonl'))], name + '-execute', expected=2)
                    result = json.loads(raw)
                    selected = ['core-five-zone-secondary-solid']
                    if result.get('ok') is not False or result.get('selected') != selected or len(result.get('cases', [])) != 1:
                        raise AssertionError('semantic control selection mismatch: ' + name)
                    failure = result['cases'][0]
                    if failure['name'] != selected[0] or failure.get('failureKind') != 'ASSERTION' or 'semantic-control complete five-zone' not in failure.get('error', ''):
                        raise AssertionError('control failed for an unrelated/compile/startup reason: ' + name)
                    planned.append('semantic-' + name)
                    results.append({'name': 'semantic-' + name, 'group': 'semantic-controls', 'status': 'PASS_EXPECTED_ASSERTION_FAILURE', 'compiled': True, 'mutantSourceSha256': sha(mutated), 'assertion': failure})
                    write_json(out / (name + '-result.json'), result)
        if selector in ('x2mini', 'x2mini-admission'):
            selector_inputs = [('unknown', ['--case', 'not-a-real-case']),
                               ('empty', ['--case', '']),
                               ('duplicate', ['--case', 'x2mini', '--case', 'x2mini-wire'])]
            for name, arguments in selector_inputs:
                rejected_scratch = out / ('must-not-exist-' + name)
                invoke([sys.executable, str(HERE / 'replay.py'), '--scratch', str(rejected_scratch), *arguments],
                       'selector-' + name, expected=2)
                if rejected_scratch.exists():
                    raise AssertionError('invalid selector entered preparation/default execution: ' + name)
                if recovery is not None and recovery.get('remainingCases'):
                    case_log = out / ('must-not-select-' + name)
                    invalid_names = {'unknown': 'not-an-original-case', 'empty': '', 'duplicate': recovery['remainingCases'][0] + ',' + recovery['remainingCases'][0]}
                    invoke(['dotnet', str(consumer), '--case', 'x2mini', '--core', str(core), '--log', str(case_log), '--cases', invalid_names[name]], 'entry-cases-' + name, expected=64)
                    if case_log.exists() or pathlib.Path(str(case_log) + '.selection.json').exists():
                        raise AssertionError('invalid exact-case selection entered execution: ' + name)
                case = {'name': 'selector-reject-' + name, 'group': 'selection', 'status': 'PASS',
                        'expectedExit': 2, 'preparedOrExecutedScenarios': 0}
                planned.append(case['name']); results.append(case)
        if selector in ('x2mini', 'x2mini-terminal'):
            generated = adapt_transport(out)
            area = build('TransportReplay.csproj', 'actual-transport', ['-p:ReplayTransportSource=' + str(generated)])
            program = area / 'bin/TransportReplay/release/X2MiniTransportReplay.dll'
            raw = invoke(['dotnet', str(program), '--case', 'x2mini-terminal'], 'transport-execute')
            consume(raw, 'transport-result', {'terminal'})
        summary.update(status='PASS', selected=planned, actual=[r['name'] for r in results], tests=results)
        if not planned or len(planned) != len(set(planned)):
            raise AssertionError('empty or duplicated actual selection')
    except BaseException as error:
        summary.update(status='FAIL', error=repr(error), selected=planned, actual=[r['name'] for r in results], tests=results)
        raise
    finally:
        if job is not None:
            job.close()
        if before is not None:
            after = input_closure(selector); write_json(out / 'inputs-after.json', after)
            summary['sourceDrift'] = sorted(k for k in set(before) | set(after) if before.get(k) != after.get(k))
            if recovery is not None:
                manifest = pathlib.Path(recovery['manifest'])
                if sha(manifest) != recovery['manifestSha256']:
                    summary['sourceDrift'].append(str(manifest))
                else:
                    for name, expected in read_json(manifest)['fileHashes'].items():
                        if not pathlib.Path(name).is_file() or sha(name) != expected:
                            summary['sourceDrift'].append(name)
            if binding is not None:
                try:
                    if bind_dotnet() != binding:
                        summary['sourceDrift'].append('exact SDK/runtime binding')
                except Exception as error:
                    summary['sourceDrift'].append('exact SDK/runtime binding: ' + repr(error))
            if summary['sourceDrift']:
                summary['status'] = 'FAIL'
        elapsed = time.monotonic() - started
        summary.update(steps=steps, elapsedSeconds=elapsed, sourceFreeze=FROZEN, runtimeBinding=binding, recovery=recovery,
                       priorChargedSeconds=historical, cumulativeChargedSeconds=historical + elapsed,
                       remainingOriginalBudgetSeconds=max(0, 240 - historical - elapsed))
        if historical + elapsed > 240:
            summary.update(status='FAIL', budgetExceeded=True)
        write_json(out / 'result.json', summary)
    if summary['status'] != 'PASS':
        raise RuntimeError('source closure changed during selected replay')
    print(json.dumps({k: v for k, v in summary.items() if k not in ('tests', 'steps')}, indent=2))
