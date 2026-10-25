"""Offline execution of exact production dual-route dispatch and wake guards.
Only this driver/fixture and --out are written. No live Steam/game/sleep calls.
"""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import os
import re
import subprocess
import sys
sys.dont_write_bytecode = True


def sha(data):
    return hashlib.sha256(data).hexdigest()


def masked(text):
    pattern = r'//[^\n]*|/\*.*?\*/|(?:u8|u|U|L)?R"(?P<tag>[^ ()\\\t\r\n]{0,16})\(.*?\)(?P=tag)"|"(?:\\.|[^"\\])*"|\x27(?:\\.|[^\x27\\])*\x27'
    return re.sub(pattern, lambda m: ''.join('\n' if c == '\n' else ' ' for c in m[0]), text, flags=re.S)


def extract(text, pattern, semicolon=False):
    clean = masked(text)
    hits = list(re.finditer(pattern, clean, re.M))
    if len(hits) != 1:
        raise RuntimeError(f'ambiguous/missing production definition: {pattern}: {len(hits)}')
    start = hits[0].start()
    opening = clean.index('{', start)
    depth, end = 1, opening + 1
    while depth and end < len(clean):
        depth += (clean[end] == '{') - (clean[end] == '}')
        end += 1
    if depth:
        raise RuntimeError('unbalanced production definition')
    if semicolon:
        while end < len(clean) and clean[end].isspace():
            end += 1
        if clean[end:end+1] != ';':
            raise RuntimeError('missing definition semicolon')
        end += 1
    return text[start:end], start, end


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', '--output', type=Path, required=True)
    parser.add_argument('--prepare-only', action='store_true')
    parser.add_argument('--negative-controls', action='store_true')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parent.parent
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    files = ['native/steam_original_recovery_runtime.h', 'native/steam_original_recovery_wake_watch.h',
             'native/steam_original_recovery_policy.h', 'native/sleep_recovery_diagnostics.h',
             'native/sleep_steam_tracking.h', 'native/steam_native_record_identity.h',
             'native/steam_native_app_state_win32.h', 'deps/json/json.hpp',
             'tools/steam_original_recovery_dual_route_fixture.cpp.in',
             'tools/steam_original_recovery_dual_route_selftest.py',
             'tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json']
    files.extend(['native/sleep_recovery_auto.h', 'native/main.cpp'])
    runtime = (repo / files[0]).read_text(encoding='utf-8-sig')
    for match in re.finditer(r'^#include "(steam_original_recovery_[^"/]*(?:route|proof)[^"/]*)"', runtime, re.M):
        files.append('native/' + match[1])
    inputs = {name: (repo / name).read_bytes() for name in dict.fromkeys(files)}
    frozen = out / 'production'
    for name, data in inputs.items():
        target = frozen / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    # Re-read only the captured snapshot for generated code; detect edits at end.
    text = inputs[files[0]].decode('utf-8-sig').replace('\r\n', '\n')
    extracts = []
    def take(name, pattern=None, semicolon=False, source=files[0]):
        content = inputs[source].decode('utf-8-sig').replace('\r\n', '\n')
        body, start, end = extract(content, pattern or r'^static\s+[^;{}]*?\b' + re.escape(name) + r'\s*\([^;{}]*\)\s*\{', semicolon)
        extracts.append({'name': name, 'source': source, 'startLine': content.count('\n', 0, start)+1,
                         'endLine': content.count('\n', 0, end)+1, 'sha256': sha(body.encode()), 'bytes': len(body.encode())})
        (out / (name + '.inc')).write_text(body, encoding='utf-8')
        return body
    parts = {'PENDING': take('Pending', r'\bstruct\s+Pending\s*\{', True),
             'CONTROL': take('Control', r'\bstruct\s+Control\s*\{', True)}
    helpers = ['identity', 'asJson', 'wakeProof', 'sameScopeNow', 'sameObservationScopeNow', 'accountCandidate', 'enqueue']
    if re.search(r'^static\s+[^;{}]*?\blegacyWakeProof\s*\(', masked(text), re.M):
        helpers.insert(3, 'legacyWakeProof')
    parts['HELPERS'] = '\n'.join(take(name) for name in helpers)
    parts['RUN_ONE'] = take('runRoute') + '\n' + take('runOne')
    event_entrypoints = ['steamOriginalRecoverySuspend', 'steamOriginalRecoveryWake', 'steamOriginalRecoveryPowerReady', 'steamOriginalRecoveryBindingReady', 'steamOriginalRecoveryRequestStop']
    parts['EVENT_ENTRYPOINTS'] = '\n'.join(take(name) for name in event_entrypoints)
    auto_producers = ['sleepRecoveryAutoArm', 'sleepRecoveryAutoCaptureSuspend', 'sleepRecoveryAutoCloseSuspendEpisode']
    parts['AUTO_PRODUCERS'] = '\n'.join(take(name, source='native/sleep_recovery_auto.h') for name in auto_producers)
    parts['POWER_COMMIT'] = take('commitPowerResume', source='native/main.cpp')
    app_body = take('gameIdMatches', r'\binline\s+bool\s+gameIdMatches\s*\([^;{}]*\)\s*\{', source='native/steam_native_app_state_win32.h')
    parts['GAME_ID_MATCHES'] = app_body
    route_headers = [name for name in inputs if re.search(r'(?:route|proof)', Path(name).name) and name.startswith('native/')]
    parts['ROUTE_INCLUDES'] = '\n'.join('#include "' + Path(name).name + '"' for name in route_headers)
    cpp = inputs['tools/steam_original_recovery_dual_route_fixture.cpp.in'].decode('utf-8-sig').replace('\r\n', '\n')
    for token, body in parts.items():
        marker = '@@' + token + '@@'
        if cpp.count(marker) != 1:
            raise RuntimeError('missing/ambiguous fixture token: ' + marker)
        cpp = cpp.replace(marker, body)
    if '@@' in cpp:
        raise RuntimeError('unreplaced fixture token')
    for name, data in inputs.items():
        if name.startswith('native/') and name != files[0]:
            (out / Path(name).name).write_bytes(data)
    (out / 'json.hpp').write_bytes(inputs['deps/json/json.hpp'])
    tools = json.loads(inputs[files[10]].decode('utf-8-sig'))['native']
    env = {k.upper(): v for k, v in os.environ.items()}
    env['PATH'] = ';'.join([str(Path(tools['compiler']).parent), 'C:/Windows/System32', 'C:/Windows'])
    env['INCLUDE'] = ';'.join(tools['include'])
    env['LIB'] = ';'.join(tools['lib'])
    for key in ['CL', '_CL_', 'LINK', '_LINK_']:
        env[key] = ''
    report = {'schema': 'ymcc.steam-original-dual-route-selftest.v1',
              'startedAtUtc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'repo': str(repo), 'extractions': extracts,
              'inputs': [{'path': name, 'sha256': sha(data), 'bytes': len(data)} for name, data in inputs.items()],
              'productionMethodsReimplemented': False,
              'realConsumers': ['complete runOne', 'complete runRoute', 'wakeProof', 'sameScopeNow', 'sameObservationScopeNow', 'Admission', 'WakeProof.valid', 'accountCandidate', 'LegacyWakeProof.valid', 'RecoveryRoute.routeName'],
              'ports': ['virtual clock and power owner state', 'current game/Steam/parent identities',
                        'tracking and private read replies', 'sofRegDWORD HKCU ActiveUser untrusted candidate; default zero', 'strict proof call counter invokes exact wakeProof', 'engine boundary records guard decisions', 'in-memory logs/evidence'],
              'sceneProcessOpens': 0, 'steamMutations': 0, 'sleepActions': 0,
              'fieldAcceptance': False, 'candidateIsNativeAccountProof': False, 'compilerSha256': sha(Path(tools['compiler']).read_bytes()),
              'limits': ['Engine execution/publisher/shared durable consume are ports; parent success-chain tests own those consumers', 'Queue drain and current owner are bounded ports; real enqueue and wake/Ready/stop entrypoints execute without OS sleep or live worker', 'No real wake, native Steam attachment, UI/input or physical Running validation'], 'runs': []}
    report['realConsumers'].extend(['enqueue', 'commitPowerResume'] + event_entrypoints + auto_producers)
    report['ports'].extend(['original power owner admission and arm/capture side effects', 'hardware write gate/message/log counters; no devices or live UI'])
    report['ports'].extend(['current owner pointer', 'SetEvent signal count', 'bounded single-event dequeue'])
    if 'legacyWakeProof' in helpers:
        report['realConsumers'].append('legacyWakeProof')
    variants = [('current', cpp, False)]
    if args.negative_controls:
        controls = [
            ('ignore-game-identity',
             'if(!(identity(target.pid,target.processCreated,target.path)==scope.game))return false;',
             'if(false)return false;'),
            ('ignore-steam-identity',
             r'return identity(steam.pid,steam.created,steam.root+L"\\steam.exe")==scope.steam;',
             r'return identity(steam.pid,steam.created,steam.root+L"\\steam.exe").valid();'),
            ('do-not-consume-wake',
             'decision=routeAdmission.admit(scope,{pending.kind,pending.generation,pending.sequence},enabled);',
             'decision=routeAdmission.preview(scope,{pending.kind,pending.generation,pending.sequence},enabled);'),
            ('ignore-kernel-confirmation',
             'proof.confirmed=ymcc::sleeprecovery::flag(admission,"kernelWakeConfirmed");',
             'proof.confirmed=true;'),
            ('static-gate-blocks-legacy',
             'const ULONGLONG powerEventFileTime=powerOwnerWitness?legacyProof.eventFileTime:proof.eventFileTime;',
             'const ULONGLONG powerEventFileTime=legacy?0:(powerOwnerWitness?legacyProof.eventFileTime:proof.eventFileTime);'),
            ('legacy-calls-strict-proof',
             'pending.kind==Trigger::Wake&&!legacy?wakeProof(pending.generation,configEpoch)',
             'pending.kind==Trigger::Wake?wakeProof(pending.generation,configEpoch)'),
            ('shared-route-admission',
             'auto& routeAdmission=legacy?c->legacyAdmission:c->admission;',
             'auto& routeAdmission=c->admission;'),
            ('ignore-registry-candidate',
             'return candidate;',
             'return DWORD{};'),
            ('ignore-final-candidate-change',
             'accountCandidate(finalSteam.account)==startingAccount',
             'accountCandidate(finalSteam.account)!=0'),
            ('ignore-final-binding',
             'const bool finalCurrent=bestOwns&&finalBinding&&',
             'const bool finalCurrent=bestOwns&&(finalBinding||true)&&'),
            ('second-route-can-publish-after-terminal',
             'runRoute(c,pending,config,route,!terminal);',
             'runRoute(c,pending,config,route,true);'),
            ('licensed-type-check-removed',
             '((std::stoull(tracked.gameId)>>24)&255)!=2',
             'false'),
            ('event-ignore-ready-gate',
             'if(g_powerLifecycle.load()==PowerLifecycle::Ready)SteamOriginalRecovery::enqueue(c,ymcc::steamOriginal::Trigger::Wake,generation);',
             'if(true)SteamOriginalRecovery::enqueue(c,ymcc::steamOriginal::Trigger::Wake,generation);'),
            ('event-drop-deferred-wake',
             'c->latestPower=generation;c->deferredWake=generation;',
             'c->latestPower=generation;'),
            ('producer-drop-commit-ready-notification',
             'g_powerLifecycle.store(PowerLifecycle::Ready, std::memory_order_release);\n    steamOriginalRecoveryPowerReady(generation);',
             'g_powerLifecycle.store(PowerLifecycle::Ready, std::memory_order_release);'),
            ('producer-drop-arm-wake-notification',
             'SleepRecoveryAuto::arm(generation);steamOriginalRecoveryWake(generation);',
             'SleepRecoveryAuto::arm(generation);'),
        ]
        for name, old, new in controls:
            if cpp.count(old) != 1:
                raise RuntimeError(f'nonunique negative control {name}: {cpp.count(old)}')
            variants.append((name, cpp.replace(old, new), True))
    expected_negative_cases = {
        'ignore-game-identity': ('game-changes-during-private-read', 'changed-game-dispatched'),
        'ignore-steam-identity': ('steam-changes-during-private-read', 'changed-steam-dispatched'),
        'do-not-consume-wake': ('same-wake-never-refunds', 'wake-budget-refunded'),
        'ignore-kernel-confirmation': ('actual-wake-proof-refuses-unconfirmed', 'unconfirmed-static-witness-accepted'),
        'static-gate-blocks-legacy': ('s3-ready-no-kernel', 's3-routes-not-dispatched'),
        'legacy-calls-strict-proof': ('s3-legacy-does-not-read-ui-gate', 'legacy-read-strict-kernel-proof'),
        'shared-route-admission': ('independent-route-admission', 'legacy-admission-consumed-static'),
        'ignore-registry-candidate': ('stale-registry-pid-valid-candidate-legacy-dispatches', 'candidate-legacy-not-dispatched'),
        'ignore-final-candidate-change': ('candidate-change-revokes-final-ownership', 'changed-candidate-resurrected-ownership'),
        'ignore-final-binding': ('same-pid-different-full-gameid-cannot-resurrect', 'migrated-gameid-resurrected-ownership'),
        'second-route-can-publish-after-terminal': ('legacy-success-static-only-observes', 'static-can-double-publish'),
        'licensed-type-check-removed': ('licensed-among-us-type0-excluded-LegacyDynamic-Startup', 'licensed-entered-engine'),
        'event-ignore-ready-gate': ('event-wake-before-ready-enters-both-routes', 'wake-queued-before-ready'),
        'event-drop-deferred-wake': ('event-wake-before-ready-enters-both-routes', 'deferred-wake-lost'),
        'producer-drop-commit-ready-notification': ('producer-commit-ready-releases-deferred-wake', 'formal-ready-commit-lost-wake'),
        'producer-drop-arm-wake-notification': ('producer-arm-preserves-wake-before-ready', 'formal-arm-lost-wake-notification'),
    }
    for name, code, negative in variants:
        target = out / name
        target.mkdir()
        source = target / 'fixture.cpp'
        source.write_text(code, encoding='utf-8')
        row = {'name': name, 'negative': negative, 'generatedSourceSha256': sha(source.read_bytes())}
        if not args.prepare_only:
            exe = target / 'fixture.exe'
            command = [tools['compiler'], '/nologo', '/std:c++20', '/utf-8', '/EHsc', '/MT', '/O1', '/W4', '/WX', '/bigobj',
                       '/I'+str(out), str(source), '/Fo'+str(target/'fixture.obj'), '/Fe'+str(exe)]
            built = subprocess.run(command, cwd=out, env=env, capture_output=True, timeout=60)
            (target/'compile.log').write_bytes(built.stdout+built.stderr)
            row['compileExit'] = built.returncode
            if built.returncode == 0:
                ran = subprocess.run([str(exe)], cwd=out, env=env, capture_output=True, timeout=25)
                (target/'run.log').write_bytes(ran.stdout+ran.stderr)
                row['runExit'] = ran.returncode
                try:
                    detail = json.loads(ran.stdout.decode('utf-8-sig'))
                    (target/'result.json').write_text(json.dumps(detail, indent=2)+'\n', encoding='utf-8')
                    row.update(detail)
                    if negative:
                        expected_case, expected_reason = expected_negative_cases[name]
                        row['expectedFailure'] = {'case': expected_case, 'reason': expected_reason}
                        row['detected'] = ran.returncode == 1 and any(case.get('name') == expected_case and case.get('reason') == expected_reason and case.get('passed') is False for case in detail.get('cases', []))
                    else:
                        row['detected'] = ran.returncode == 0 and detail.get('passed') is True
                except Exception as exc:
                    row['parseError'] = str(exc)
        report['runs'].append(row)
    report['sourceDrift'] = [name for name, data in inputs.items() if (repo/name).read_bytes() != data]
    report['preparedOnly'] = args.prepare_only
    report['success'] = not args.prepare_only and not report['sourceDrift'] and all(row.get('compileExit') == 0 and row.get('detected') is True for row in report['runs'])
    (out/'report.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    summary = [{k: v for k, v in row.items() if k != 'cases'} | {'failedCases': [case for case in row.get('cases', []) if case.get('passed') is False] if not row['negative'] else []} for row in report['runs']]
    print(json.dumps({'success': report['success'], 'preparedOnly': args.prepare_only, 'sourceDrift': report['sourceDrift'], 'runs': summary, 'report': str(out/'report.json')}, indent=2))
    return 0 if report['success'] or args.prepare_only else 1


if __name__ == '__main__':
    raise SystemExit(main())
