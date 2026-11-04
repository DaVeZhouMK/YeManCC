"""Fixed HC source oracle generation. Never imports the product codec.

Only BCL type/terminal bindings and a recording Thread alias differ. Extracted
function text, byte arrays, inheritance facts and both report lengths are bound
to exact upstream hashes. A descriptor from physical hardware remains unverified.
"""
import hashlib
import json
import pathlib
import re
from extract_pinned import function

HERE = pathlib.Path(__file__).resolve().parent
X2_SOURCE = {'sourceRoot': 'G:/YeManCC-Work/Testing/YMCC/Evidence/HC-Code-Capability-Upgrade-20261031/upstream/source/HandheldCompanion',
 'sourceCommit': 'f2e58468d9917189d4be7ccfc242385ce2c3035b',
 'files': {'Devices/OneXPlayer/OneXPlayerX2MiniPro.cs': '7f06350daeb1d1c7157ae21f1dc231637394008256b3fc4c22c747109e2c7b67',
           'Devices/OneXPlayer/OneXPlayerX2.cs': '41cf648a9d9247c2e1a5da29bed748f0aed0a8407202a690b0b866ae2d5c369b',
           'Devices/OneXPlayer/OneXPlayerX1.cs': '3b80d932227c3beddbb6b3a4007c57fc19e0fb55df0db54d389ca8834eae3485',
           'Devices/OneXPlayer/OneXAOKZOE.cs': '30661f7bfba6b67ba9f025be97158c274fe28cabdcd0faac08ca3fb94c34d718',
           'Devices/IDevice.cs': 'f608b6f5a165e8b74a48d09d81fa5316c72273c0efd721327b1d2deccf1d5a47'},
 'inheritance': ['OneXPlayerX2MiniPro : OneXPlayerX2',
                 'OneXPlayerX2 : OneXPlayerX1',
                 'OneXPlayerX1 : OneXAOKZOE'],
 'methods': {'Devices/OneXPlayer/OneXPlayerX2MiniPro.cs': ['SetLedBrightness', 'SetLedColor'],
             'Devices/OneXPlayer/OneXAOKZOE.cs': ['BuildV1Command', 'SendV1Brightness', 'SendV1SolidColor'],
             'Devices/IDevice.cs': ['WithReportID']},
 'reportLengths': {'BuildV1CommandPayload': 64, 'WithReportIDDefault': 65},
 'descriptorOutputReportByteLength': {'value': 65,
                                      'basis': 'WithReportID default 1+64; checked by actual '
                                               'WindowsHidTransport; physical descriptor NOT_VERIFIED'},
 'hardwareVerified': False,
 'methodSha256': {'Devices/OneXPlayer/OneXPlayerX2MiniPro.cs': {'SetLedBrightness': '6fed2f56fe9c4e4647f5d4b0cb5b34604b93dd78346442e3c12359e9a4b2ee25',
                                                                'SetLedColor': 'f60c2a17209f663a80b83f03631174d9865921157d1ad53f5f912fdc5d5198f3'},
                  'Devices/OneXPlayer/OneXAOKZOE.cs': {'BuildV1Command': '143196d9e8f4c50539ef0b3c0c02fc3151f755c3405856f753b6471ec1b87bf7',
                                                       'SendV1Brightness': '857b74a631910a96eca11faf27a46c8cd29ddcb34cb0e55621108661413c6be2',
                                                       'SendV1SolidColor': '8eaed376df206df522d948de7cc69051afc40a9e4d5aff39a74748041810db42'},
                  'Devices/IDevice.cs': {'WithReportID': '8b76bd52e824bfef90a6a4e532a0ebc0bb9020ffe627becc28c2a03d0b4477d1'}}}

def verify_sources():
    lock = json.loads((HERE / 'source-lock.json').read_text(encoding='utf-8-sig'))
    if lock.get('exactExtensions', {}).get('onex-x2mini-hid-v1') != X2_SOURCE:
        raise RuntimeError('X2 exact source facts changed')
    texts = {}
    for relative, expected in X2_SOURCE['files'].items():
        data = (pathlib.Path(X2_SOURCE['sourceRoot']) / relative).read_bytes()
        if hashlib.sha256(data).hexdigest() != expected:
            raise RuntimeError('fixed X2 source drift: ' + relative)
        texts[relative] = data.decode('utf-8-sig').replace('\r\n', '\n')
    for relative, declaration in zip(list(X2_SOURCE['files'])[:3], X2_SOURCE['inheritance']):
        if declaration not in texts[relative]:
            raise RuntimeError('fixed X2 inheritance drift: ' + relative)
    return texts


def generate_oracle(destination):
    texts = verify_sources()
    common = texts['Devices/OneXPlayer/OneXAOKZOE.cs']
    identity = texts['Devices/IDevice.cs']
    device = texts['Devices/OneXPlayer/OneXPlayerX2MiniPro.cs']
    methods = [function(common, n) for n in ('BuildV1Command', 'SendV1Brightness', 'SendV1SolidColor')]
    methods.append(function(identity, 'WithReportID'))
    device_methods = [function(device, n).replace('System.Windows.Media.Color', 'OracleColor').replace('HidLibrary.HidDevice', 'OracleHid')
                      for n in ('SetLedBrightness', 'SetLedColor')]
    for relative, names in X2_SOURCE['methodSha256'].items():
        for name, expected in names.items():
            if hashlib.sha256(function(texts[relative], name).encode()).hexdigest() != expected:
                raise RuntimeError('fixed source method drift: ' + relative + ':' + name)
    # The source's WriteReport implementation handles application queues and
    # dependencies. Only this boundary is replaced by a report recorder.
    generated = '''// Fixed source oracle: exact hashes in source-lock.json. No product codec.
#nullable enable
using Color = OracleColor;
using HidDevice = OracleHid;
using Thread = OracleThread;
using LEDLevel = OracleLevel;
[Flags] internal enum OracleLevel { SolidColor = 1, Breathing = 2, Rainbow = 4, LEDPreset = 128 }
internal readonly record struct OracleColor(byte R, byte G, byte B);
internal sealed class OracleHid { internal bool IsConnected = true; internal readonly List<byte[]> Reports = new(); }
internal static class OracleThread {
    internal static readonly List<int> Delays = new();
    internal static void Sleep(int milliseconds) => Delays.Add(milliseconds);
}
internal abstract class OracleBase {
    protected const int VendorHidId = 0;
    protected readonly Dictionary<int, OracleHid> hidDevices = new();
    public abstract bool SetLedColor(OracleColor mainColor, OracleColor secondaryColor, OracleLevel level, int speed = 100);
    public abstract bool SetLedBrightness(int brightness);
    protected static bool WriteReport(OracleHid device, byte[] report) { device.Reports.Add(report); return true; }
'''
    generated += '\n'.join(methods) + '\n}\n'
    generated += '''internal sealed class FixedSourceOracle : OracleBase {
    internal readonly OracleHid Terminal = new();
    internal FixedSourceOracle() { hidDevices[VendorHidId] = Terminal; OracleThread.Delays.Clear(); }
'''
    generated += '\n'.join(device_methods) + '\n}\n'
    destination.write_text(generated, encoding='utf-8', newline='\n')
    provenance = {'source': X2_SOURCE, 'generatedSha256': hashlib.sha256(destination.read_bytes()).hexdigest(),
                  'adaptations': ['BCL color, level and HID aliases', 'recording WriteReport terminal',
                                  'recording Thread.Sleep alias (source delay unchanged)',
                                  'minimal base and vendor dictionary scaffolding'],
                  'methods': {f: {n: hashlib.sha256(function(texts[f], n).encode()).hexdigest() for n in names}
                              for f, names in X2_SOURCE['methods'].items()},
                  'productCodecUsedForOracle': False}
    destination.with_suffix('.source.json').write_text(json.dumps(provenance, indent=2) + '\n', encoding='utf-8')
    return provenance

def generate_private_owner(directory, namespace):
    """Only replace the two adjacent IPC names in the actual owner source."""
    import difflib
    owner = HERE.parents[2] / 'FanLab/LightSetter/Oxp3OwnerLease.cs'
    original = owner.read_text(encoding='utf-8-sig')
    generated = original
    renames = [('YeManCC-OXP3-VendorRevoked-', namespace + '-Revoked-'),
               ('YeManCC-OXP3-OwnerLease', namespace + '-OwnerLease')]
    for old, new in renames:
        if generated.count(old) != 1:
            raise RuntimeError('owner IPC seam drift: ' + old)
        generated = generated.replace(old, new)
    restored = generated
    for old, new in renames:
        restored = restored.replace(new, old)
    patch = ''.join(difflib.unified_diff(original.splitlines(True), generated.splitlines(True),
                                       fromfile='actual/Oxp3OwnerLease.cs', tofile='scratch/PrivateOwnerLease.cs'))
    if restored != original or sum(line.startswith('@@ ') for line in patch.splitlines()) != 1:
        raise RuntimeError('private owner is not one reversible namespace-only hunk')
    destination = directory / 'PrivateOwnerLease.cs'
    destination.write_text(generated, encoding='utf-8', newline='\n')
    (directory / 'private-owner-names.patch').write_text(patch, encoding='utf-8')
    provenance = {'source': str(owner), 'sha256': hashlib.sha256(owner.read_bytes()).hexdigest(),
                  'generatedSha256': hashlib.sha256(destination.read_bytes()).hexdigest(),
                  'changes': [{'old': old, 'new': new, 'occurrences': 1} for old, new in renames],
                  'singleHunk': True, 'reverseNormalizedTextEqual': True,
                  'patchSha256': hashlib.sha256(patch.encode()).hexdigest(), 'privateScope': namespace,
                  'offsetsVersionTagsAndLeaseLogicUnchanged': True, 'fixedProductObjectsOpenedOrWritten': False}
    (directory / 'owner-source.json').write_text(json.dumps(provenance, indent=2) + '\n', encoding='utf-8')
    return destination
