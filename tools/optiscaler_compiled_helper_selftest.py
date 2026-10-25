#!/usr/bin/env python3
"""Compiled helper regression, entirely inside a caller-selected fixture directory.
No real games, AppData, anti-cheat services or Steam settings are modified.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--helper', required=True)
parser.add_argument('--fixtures', required=True)
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
util.package(cache, '0.9.5-pre4')
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
    manifest = json.loads((Path(installed['backup']) / 'manifest.json').read_text(encoding='utf-8'))
    assert manifest['anti_cheat_risk_acknowledged']
    cases.append(backend + ': acknowledged deployment does not alter executable/anti-cheat or create AppID')
    assert run(['uninstall', str(game)], 0)['ok'] and snapshot() == original
    cases.append(backend + ': restore returns fixture files byte-for-byte')
assert not (game / 'steam_appid.txt').exists()
report = {'suite': 'Compiled Opti helper fixture', 'passed': len(cases), 'cases': cases,
          'helper': str(helper), 'fixtures': str(root),
          'scope': 'Child APPDATA/LOCALAPPDATA isolated; inert fake DLL/EXE files; no game/client launch or anti-cheat bypass'}
(root.parent / 'helper-results.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(report, ensure_ascii=True, indent=2))
