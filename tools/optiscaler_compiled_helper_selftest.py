#!/usr/bin/env python3
"""Compiled helper regression, entirely inside a caller-selected fixture directory.
No real games, AppData, anti-cheat services or Steam settings are modified.
"""
import argparse
import configparser
import hashlib
import importlib.util
import json
import os
import re
import shutil
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--helper', required=True)
parser.add_argument('--fixtures', required=True)
parser.add_argument('--official-dx12-sr', type=Path, help='Extracted OptiScaler v0.9.5 final AMD DX12 SR DLL')
args = parser.parse_args()
root = Path(args.fixtures).resolve()
root.mkdir(parents=True, exist_ok=True)
helper = Path(args.helper).resolve()
source_root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('opti_fixture_util', source_root / 'tools/optiscaler_selftest.py')
util = importlib.util.module_from_spec(spec)
spec.loader.exec_module(util)
roaming = root / 'Roaming'
cache = roaming / 'OptiscalerClient/Cache'
legacy = util.package(cache, '0.9.5-pre4')
util.dll(cache / 'Extras/FSR_4.1.1b/amd_fidelityfx_upscaler_dx12.dll', b'fixture-int8')
util.dll(cache / 'OptiPatcher/0.41/OptiPatcher.asi')
game = root / 'Game'
game.mkdir(exist_ok=True)
(game / 'start_protected_game.exe').write_bytes(b'fixture-anticheat-not-executable')
(game / 'Game.exe').write_bytes(b'fixture-game-not-executable')
original = {str(p.relative_to(game)): p.read_bytes() for p in game.rglob('*') if p.is_file()}
env = dict(os.environ, APPDATA=str(roaming), LOCALAPPDATA=str(root / 'Local'))
# A caller's import/build environment must not alter the frozen helper runtime.
env.pop('PYTHONPATH', None)
cases = []

def snapshot():
    return {str(p.relative_to(game)): p.read_bytes() for p in game.rglob('*') if p.is_file()}

def run(arguments, expected):
    process = subprocess.run([str(helper), 'optiscaler', *arguments], env=env, cwd=root,
                             capture_output=True, text=True, timeout=30,
                             creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
    assert process.returncode == expected, (arguments, process.returncode, process.stdout, process.stderr)
    return json.loads(process.stdout)

analysis = run(['analyze', str(game)], 0)
assert analysis['ok'] and analysis['anti_cheat'] and analysis['scan_complete']
assert Path(analysis['source']).resolve().is_relative_to(root)
cases.append('Compiled analysis uses isolated fixture cache and retains anti-cheat finding')
for backend in ('fsr', 'xess'):
    denied = run(['install', str(game), '--backend', backend], 7)
    assert denied['requires_anti_cheat_confirmation'] and snapshot() == original
    cases.append(backend + ': default denial never changes game files')
    dry = run(['install', str(game), '--backend', backend, '--acknowledge-anticheat-risk', '--dry-run'], 0)
    assert dry['ok'] and snapshot() == original
    cases.append(backend + ': acknowledged dry-run never writes')
    installed = run(['install', str(game), '--backend', backend, '--acknowledge-anticheat-risk'], 0)
    assert installed['ok'] and installed['anti_cheat']
    assert all((game / rel).read_bytes() == content for rel, content in original.items())
    assert not (game / 'steam_appid.txt').exists()
    assert (game / 'OptiScaler.ini').exists()
    assert installed['version'] == '0.9.5-pre4'
    upscaler = 'amd_fidelityfx_upscaler_dx12.dll'
    assert (game / upscaler).read_bytes() == (legacy / upscaler).read_bytes()
    manifest = json.loads((Path(installed['backup']) / 'manifest.json').read_text(encoding='utf-8'))
    assert manifest['anti_cheat_risk_acknowledged']
    cases.append(backend + ': acknowledged deployment does not alter executable/anti-cheat or create AppID')
    assert run(['uninstall', str(game)], 0)['ok'] and snapshot() == original
    cases.append(backend + ': restore returns fixture files byte-for-byte')
assert not (game / 'steam_appid.txt').exists()
stable = util.package(cache, '0.9.5')
stable_ini = stable / 'OptiScaler.ini'
stable_ini.write_text(stable_ini.read_text(encoding='utf-8') + '\n[FSR]\nFsr4EnableWatermark=true\n', encoding='utf-8')
upscaler = 'amd_fidelityfx_upscaler_dx12.dll'
util.dll(stable / upscaler, b'same-name-nonofficial-x64-pe')
rejected = run(['analyze', str(game)], 0)
assert rejected['version'] == '0.9.5-pre4'
assert any('官方 FSR 4.1.2' in warning for warning in rejected['warnings'])
assert snapshot() == original
cases.append('A same-named nonofficial stable DX12 SR is ignored before installation')
if args.official_dx12_sr:
    shutil.copyfile(args.official_dx12_sr, stable / upscaler)
    assert run(['analyze', str(game)], 0)['version'] == '0.9.5'
    for backend in ('fsr', 'xess'):
        installed = run(['install', str(game), '--backend', backend, '--acknowledge-anticheat-risk'], 0)
        assert installed['ok'] and installed['version'] == '0.9.5'
        assert (game / upscaler).read_bytes() == (stable / upscaler).read_bytes()
        ini = configparser.ConfigParser()
        ini.read(game / 'OptiScaler.ini', encoding='utf-8')
        assert ini['FSR']['Fsr4ForceEnableInt8'] == 'auto'
        assert ini['UpscaleRatio']['UpscaleRatioOverrideEnabled'] == 'false'
        assert ini['FSR']['Fsr4EnableWatermark'] == ('false' if backend == 'fsr' else 'true')
        assert run(['uninstall', str(game)], 0)['ok'] and snapshot() == original
        cases.append(backend + ': stable 0.9.5 uses the bundled SDK and restores the fixture byte-for-byte')
# OPT stores an Unreal/Cyberpunk card under its root, not the nested EXE directory.
card_root = root / 'Nested Example'
game = card_root / 'Binaries' / 'Win64'
game.mkdir(parents=True)
exe = game / 'Launch.exe'
exe.write_bytes(b'fixture-exe')
proxy = game / 'dxgi.dll'
proxy.write_bytes(b'fixture-game-original')
original = snapshot()
catalog = roaming / 'OptiscalerClient' / 'games.json'
catalog.write_text(json.dumps([{'InstallPath': str(card_root), 'ExecutablePath': str(exe), 'AppId': 'Manual_fixture'}]), encoding='utf-8')
normalized = card_root.as_posix().lower()
slug = '_'.join(re.sub(r'[^\w-]', '_', s) for s in normalized.split('/')[-2:]) + '_' + hashlib.sha256(normalized.encode()).hexdigest()[:8]
slot = catalog.parent / 'Backups' / slug
install = ['install', str(game), '--backend', 'fsr', '--game-exe', str(exe)]
remove = ['uninstall', str(game), '--game-exe', str(exe)]
installed = run(install, 0)
assert Path(installed['backup']) == slot and len(list(slot.parent.iterdir())) == 1
manifest_file = slot / 'manifest.json'
manifest = json.loads(manifest_file.read_text('utf-8'))
assert manifest['ManifestVersion'] == 2 and manifest['OperationStatus'] == 'committed'
assert Path(manifest['InstalledGameDirectory']) == game
assert set(manifest['InstalledFiles']) == {x['RelativePath'] for x in manifest['FilesCreated'] + manifest['FilesOverwritten']}
rec, = manifest['FilesOverwritten']
assert rec['RelativePath'] == 'dxgi.dll'
assert (slot / 'files' / rec['BackupRelativePath']).read_bytes() == original['dxgi.dll']
assert rec['PostInstallSha256'] == hashlib.sha256(proxy.read_bytes()).hexdigest()
assert rec['PreInstallSha256'] == hashlib.sha256(original['dxgi.dll']).hexdigest()
assert run(install, 7)['ok'] is False
cases.append('Nested executable uses the client card install-root key and a single committed v2 backup')
proxy.write_bytes(b'post-install-conflict')
modified = snapshot()
assert run(remove, 7)['ok'] is False and snapshot() == modified and manifest_file.exists()
proxy.unlink()  # manually deleting deployed files must not discard the original
(game / 'OptiScaler.ini').unlink()
assert run(remove, 0)['via'] == 'optiscalerclient' and snapshot() == original and not slot.exists()
cases.append('Conflicting binary is preserved; missing deployed files still restore original and retire slot')

# Exercise the opposite direction with a client-shaped ledger and its native file layout.
slot.mkdir()
(slot / 'files').mkdir()
(slot / 'files' / 'dxgi.dll').write_bytes(original['dxgi.dll'])
proxy.write_bytes(b'official-client-installed-proxy')
(game / 'OptiScaler.ini').write_bytes(b'official-client-config')
rec['BackupRelativePath'] = 'dxgi.dll'
rec['PostInstallSha256'] = hashlib.sha256(proxy.read_bytes()).hexdigest().upper()
manifest['InstalledFiles'] = ['dxgi.dll', 'OptiScaler.ini']
manifest['FilesCreated'] = [{'RelativePath': 'OptiScaler.ini', 'ExistedBefore': False}]
manifest['PreInstallKeyFiles'] = [r for r in manifest['PreInstallKeyFiles'] if r['RelativePath'] in manifest['InstalledFiles']]
manifest['InstalledDirectories'] = []
manifest.pop('YmccBackend')
manifest_file.write_text(json.dumps(manifest), encoding='utf-8-sig')
assert run(remove, 0)['ok'] and snapshot() == original and not slot.exists()
assert run(install, 0)['ok'] and run(remove, 0)['ok'] and snapshot() == original
cases.append('OPT client ledger restores byte-for-byte in YMCC, then reinstall and restore succeeds')

assert run(install, 0)['ok']
unknown = slot / 'files' / 'unrecognized-original.dat'
unknown.write_bytes(b'keep-unknown-for-recovery')
assert run(remove, 0)['warnings'] and snapshot() == original and unknown.exists()
unknown.unlink()
assert run(remove, 0)['ok'] and not slot.exists()
cases.append('Unknown backup contents are retained; known originals can still be verified and restored')

(game / 'plugins').mkdir()
(game / 'plugins' / 'existing-mod.ini').write_bytes(b'keep-me')
with_plugins = snapshot()
assert not run(install, 7)['ok'] and snapshot() == with_plugins and not slot.exists()
cases.append('Pre-existing plugin content is protected from aggressive client uninstall')

report = {'suite': 'Compiled Opti helper fixture', 'passed': len(cases), 'cases': cases,
          'helper': str(helper), 'fixtures': str(root),
          'stableRuntime': 'tested with official bytes' if args.official_dx12_sr else 'NOT_RUN (official DLL not supplied)',
          'scope': 'Child APPDATA/LOCALAPPDATA isolated; inert fake DLL/EXE files; no game/client launch or anti-cheat bypass'}
(root.parent / 'helper-results.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(report, ensure_ascii=True, indent=2))
